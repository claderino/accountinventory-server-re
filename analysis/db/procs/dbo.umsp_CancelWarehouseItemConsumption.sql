-- ===========================================================
-- Title		:	umsp_CancelWarehouseItemConsumption
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-15
-- Description	:	Forced Operation
-- ===========================================================
CREATE PROCEDURE dbo.umsp_CancelWarehouseItemConsumption
(
	@ItemInstanceID		BIGINT,
	@OwnerAccountID		UNIQUEIDENTIFIER
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
		
		DECLARE	@ownerAccountIdStr	NCHAR(36)	= CAST(@OwnerAccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	ISNULL(@ItemInstanceID, -1) <= 0
				OR	@OwnerAccountID IS NULL		)
			RAISERROR(N'invalid parameter, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @ItemInstanceID, @ownerAccountIdStr);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			UPDATE	dbo.WarehouseItem
			SET		ItemState			= 1,	-- Usable
					UserServiceType		= NULL,
					LastStateUpdateTime	= GETDATE()
			WHERE	ItemInstanceID		= @ItemInstanceID
			AND		OwnerAccountID		= @OwnerAccountID
			AND		(
						(
							ItemState	= 1				-- Usable
							AND	UserServiceType IS NULL
						)
						OR
						(
							ItemState	= 2				-- Exportation Try
							AND	UserServiceType	= 2		-- Lobby Service
						)
					);
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'failed to cancel warehouse item consumption; there is no such item or invalid item state, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																													@ItemInstanceID, @ownerAccountIdStr);
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
