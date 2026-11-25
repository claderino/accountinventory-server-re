-- ===========================================================
-- Title		:	usp_LoadUncompletedWarehouseRegistrationList
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2014-06-23
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_LoadUncompletedWarehouseRegistrationList
(
	@LoadCount	INT,
	@LoadKey	BIGINT
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		-- Parameter validity check
		IF	(	ISNULL(@LoadCount, -1) <= 0
				OR	ISNULL(@LoadKey, -1) < 0	)
			RAISERROR(N'invalid parameter, loadCount:%d, loadKey:0x%I64X', 16, @ERR_FAILURE, @LoadCount, @LoadKey);
		
		BEGIN
			SELECT	TOP(@LoadCount)
					LabelID,
					OwnerAccountID,
					GoodsID
			FROM	dbo.WarehouseGoods
			WITH	(INDEX(IX_WarehouseGoods_RegistrationState_LabelID))
			WHERE	RegistrationState	= 1		-- Registration Ready
			AND		LabelID				> @LoadKey;
		END
		
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
