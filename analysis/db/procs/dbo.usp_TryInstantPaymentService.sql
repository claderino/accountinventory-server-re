-- ===========================================================
-- Title		:	usp_TryInstantPaymentService
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2014-02-19
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_TryInstantPaymentService
(
	@ServiceID		BIGINT				OUTPUT,
	@AccountID		UNIQUEIDENTIFIER,
	@WorldID		SMALLINT,
	@PCID			BIGINT,
	@ServiceType	INT,
	@GoodsNumber	INT
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	SET	@ServiceID	= 0;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		DECLARE	@accountIdStr	NCHAR(36)	= CAST(@AccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	@AccountID IS NULL
				OR	ISNULL(@WorldID, -1) <= 0
				OR	ISNULL(@PCID, -1) <= 0
				OR	ISNULL(@ServiceType, -1) <= 0
				OR	@GoodsNumber IS NULL	)
			RAISERROR(N'invalid parameter, accountId:%s, worldId:%d, pcId:0x%I64X, serviceType:%d, goodsNumber:%d', 16, @ERR_FAILURE,
											@accountIdStr, @WorldID, @PCID, @ServiceType, @GoodsNumber);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			DECLARE	@registrationTime	DATETIME	= GETDATE();
			
			INSERT INTO dbo.InstantPaymentService (
				AccountID,
				WorldID,
				PCID,
				ServiceType,
				GoodsNumber,
				PurchaseID,
				ServiceState,
				RegistrationTime,
				LastStateUpdateTime
			)
			VALUES (
				@AccountID,
				@WorldID,
				@PCID,
				@ServiceType,
				@GoodsNumber,
				NULL,
				1,
				@registrationTime,
				@registrationTime
			);
			SET	@ServiceID	= SCOPE_IDENTITY();
		END
		
		IF	(@INIT_TRANCOUNT = 0)
			COMMIT;
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
